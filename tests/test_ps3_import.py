"""Synthetic encrypted containers only. Fixtures use arbitrary test keys."""
import hashlib
import hmac
import io
import json
from pathlib import Path
import struct
import tarfile
import tempfile
import threading
import unittest
from unittest.mock import patch
import zlib

from Crypto.Cipher import AES
from launcher import ps3_crypto as crypto, ps3_import as importer
from launcher.setup_pipeline import Cancelled, check_cancel
from launcher import setup_pipeline as pipeline
from launcher.game_profiles import PROFILES
from test_setup_pipeline import sfo


ROOT_KEY, ROOT_IV = bytes(range(32)), bytes(range(16))
PUP_KEY = bytes(range(64))
NP_KEY, NP_FREE = bytes([61]) * 16, bytes([73]) * 16


def encrypt_ctr(data, key, iv):
    return AES.new(key, AES.MODE_CTR, nonce=b"", initial_value=int.from_bytes(iv, "big")).encrypt(data)


def tar_bytes(entries):
    result = io.BytesIO()
    with tarfile.open(fileobj=result, mode="w") as archive:
        for name, content in entries:
            member = tarfile.TarInfo(name)
            if isinstance(content, tarfile.TarInfo):
                member.type, member.linkname = content.type, content.linkname
                archive.addfile(member)
            else:
                member.size = len(content)
                archive.addfile(member, io.BytesIO(content))
    return result.getvalue()


def certified(payloads, *, category=1, compressed=True, npdrm=False, signing=2):
    """Build a small container from the documented on-disk structs.

    SELF includes a TLS PHDR aliasing LOAD data, plus a section-header table
    at a different file offset. System packages contain three sections.
    """
    kind, revision, metadata_at, header_size = 8 if npdrm else 4, 7, 0x300, 0x1000
    header = bytearray(header_size)
    struct.pack_into(">4sIHHIQQ", header, 0, b"SCE\0", 2, revision, category, metadata_at - 32, header_size, sum(map(len, payloads)))
    if category == 1:
        struct.pack_into(">10Q", header, 32, 3, 0x80, 0xa0, 0xe0, 0x150, 0x190, 0, 0x220, 0xa0 if npdrm else 0, 0)
        struct.pack_into(">QIIQ", header, 0x80, 0, 0, kind, 0)
        elf = bytearray(64)
        elf[:7] = b"\x7fELF\x02\x02\x01"
        struct.pack_into(">HHIQQQI6H", elf, 16, 2, 21, 1, 0x10000, 64, 0x400, 0, 64, 56, 2, 64, 1, 0)
        header[0xa0:0xe0] = elf
        phdr = struct.pack(">II6Q", 1, 5, 0x200, 0x10000, 0x10000, len(payloads[0]), len(payloads[0]), 16)
        phdr += struct.pack(">II6Q", 7, 4, 0x200, 0x10000, 0x10000, 4, 4, 4)
        header[0xe0:0x150] = phdr
        shdr = bytes(64)
        header[0x150:0x190] = shdr
        expected = bytearray(0x440)
        expected[:64], expected[64:176], expected[0x400:] = elf, phdr, shdr
        expected[0x200:0x200 + len(payloads[0])] = payloads[0]
    else:
        expected = None
    attributes, records, body = bytearray(), bytearray(), bytearray()
    for i, payload in enumerate(payloads):
        wire = zlib.compress(payload) if compressed else payload
        key, iv = bytes([i + 21]) * 16, bytes([i + 31]) * 16
        digest_at = len(attributes) // 16
        if signing == 2:
            signing_key = bytes([i + 41]) * 64
            attributes += hmac.digest(signing_key, wire, "sha1") + bytes(12) + signing_key
        elif signing == 3:
            attributes += hashlib.sha1(wire).digest() + bytes(12)
        else:
            signing_key = bytes([i + 41]) * 32
            attributes += hmac.digest(signing_key, wire, "sha256") + signing_key
        key_at = len(attributes) // 16
        attributes += key + iv
        records += struct.pack(">QQ8I", header_size + len(body), len(wire), 2 if category == 1 else i + 1,
                               i if category == 1 else i + 1, signing, digest_at, 3, key_at, key_at + 1, 2 if compressed else 1)
        body += encrypt_ctr(wire, key, iv)
    metadata = struct.pack(">QIIIIQ", 0, 1, len(payloads), len(attributes) // 16, 0, 0) + records + attributes
    metadata += bytes(header_size - metadata_at - 64 - len(metadata))
    meta_key, meta_iv = bytes([81]) * 16, bytes([91]) * 16
    root = meta_key + bytes(16) + meta_iv + bytes(16)
    root = AES.new(ROOT_KEY, AES.MODE_CBC, iv=ROOT_IV).encrypt(root)
    if npdrm:
        struct.pack_into(">IIQ4sIII", header, 0x220, 3, 0xa0, 0, b"NPD\0", 1, 3, 0x21)
        wrapping_key = AES.new(NP_KEY, AES.MODE_ECB).decrypt(NP_FREE)
        root = AES.new(wrapping_key, AES.MODE_CBC, iv=bytes(16)).encrypt(root)
    header[metadata_at:metadata_at + 64] = root
    header[metadata_at + 64:] = encrypt_ctr(metadata, meta_key, meta_iv)
    return bytes(header + body), bytes(expected) if expected is not None else None


def pup_bytes(archive):
    header_size, count = 0x90, 1
    header = bytearray(header_size)
    struct.pack_into(">8s5Q", header, 0, b"SCEUF\0\0\0", 1, 0, count, header_size, len(archive))
    struct.pack_into(">QQQII", header, 48, 0x300, header_size, len(archive), 0, 0)
    struct.pack_into(">Q20sI", header, 80, 0, hmac.digest(PUP_KEY, archive, "sha1"), 0)
    header[112:132] = hmac.digest(PUP_KEY, header[:112], "sha1")
    return bytes(header) + archive


def pkg_bytes(files, *, debug=False):
    codec = importer.pkg_module()
    count = len(files)
    names = bytearray()
    for name, _ in files:
        names += name.encode()
    data_at = count * 32 + len(names)
    table, names, payload = bytearray(), bytearray(), bytearray()
    for name, content in files:
        encoded = name.encode()
        table += struct.pack(">IIQQII", count * 32 + len(names), len(encoded), data_at + len(payload), len(content), 3, 0)
        names += encoded
        payload += content
    plain = bytes(table + names + payload)
    header = bytearray(0xc0)
    header[:4] = b"\x7fPKG"
    struct.pack_into(">HH", header, 4, 0 if debug else 0x8000, 1)
    struct.pack_into(">I", header, 0x14, count)
    struct.pack_into(">QQQ", header, 0x18, len(header) + len(plain), len(header), len(plain))
    header[0x60:0x80] = bytes(range(32))
    if debug:
        seed = header[0x60:0x68] * 2 + header[0x68:0x70] * 2
        keystream = b"".join(hashlib.sha1(seed + bytes(24) + i.to_bytes(8, "big")).digest()[:16]
                             for i in range((len(plain) + 15) // 16))
        encrypted = bytes(a ^ b for a, b in zip(plain, keystream))
    else:
        encrypted = encrypt_ctr(plain, codec.PS3_GPKG_KEY, header[0x70:0x80])
    return bytes(header) + encrypted


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.document = dict(format=1, roots=[dict(category=c, self_type=t, revision=7, erk=ROOT_KEY.hex(), riv=ROOT_IV.hex())
                                             for c, t in ((1, 4), (1, 8), (3, 0))],
                             named=dict(pup_hmac=PUP_KEY.hex(), np_klic_key=NP_KEY.hex(), np_klic_free=NP_FREE.hex()))
        self.keyfile = self.root / "private.keys"
        self.keyfile.write_text(json.dumps(self.document))
        self.keys = crypto.KeyStore(self.keyfile)
        self.event = threading.Event()
        self.cancel = lambda: check_cancel(self.event)
        self.logs = []

    def tearDown(self):
        self.temp.cleanup()

    def test_self_reconstructs_exact_elf_and_checks_all_supported_digests(self):
        for compressed in (True, False):
            for signing in (2, 3, 6):
                wire, expected = certified([b"independent fixture payload"], compressed=compressed, signing=signing)
                actual = crypto.CertifiedFile(wire, self.keys).elf()
                self.assertEqual(actual, expected)
                damaged = wire[:-1] + bytes([wire[-1] ^ 1])
                with self.assertRaisesRegex(ValueError, "digest mismatch"):
                    crypto.CertifiedFile(damaged, self.keys).elf()

    def test_free_npdrm_update_and_missing_keys(self):
        wire, expected = certified([b"NPDRM disc update"], npdrm=True)
        self.assertEqual(crypto.CertifiedFile(wire, self.keys).elf(), expected)
        with self.assertRaisesRegex(ValueError, "np_klic_key"):
            crypto.CertifiedFile(wire, crypto.KeyStore())
        encrypted, _ = certified([b"normal SELF"])
        with self.assertRaisesRegex(ValueError, "No matching decryption key"):
            crypto.CertifiedFile(encrypted, crypto.KeyStore())
        self.event.set()
        with self.assertRaises(Cancelled):
            crypto.CertifiedFile(encrypted, self.keys, self.cancel)

    def test_key_file_errors_do_not_disclose_values_and_scetool_repeated_sections(self):
        secret = "test-private-material-must-not-appear"
        self.keyfile.write_text(json.dumps(dict(format=1, roots=[dict(category=1, erk=secret, riv="")])))
        with self.assertRaises(ValueError) as caught:
            crypto.KeyStore(self.keyfile)
        self.assertNotIn(secret, str(caught.exception))
        self.keyfile.write_text("".join(f"[pkg]\ntype=PKG\nrevision={i:02x}\nerk={ROOT_KEY.hex()}\nriv={ROOT_IV.hex()}\n" for i in range(2)))
        self.assertEqual(len(crypto.KeyStore(self.keyfile).roots), 2)

    def test_bad_container_ranges_and_decompression_bomb(self):
        wire, _ = certified([b"data"])
        for size in (0, 4, 31, 128):
            with self.assertRaises(ValueError):
                crypto.CertifiedFile(wire[:size], self.keys)
        bad = bytearray(wire)
        struct.pack_into(">Q", bad, 0x10, 2**63)
        with self.assertRaisesRegex(ValueError, "header size"):
            crypto.CertifiedFile(bytes(bad), self.keys)
        with self.assertRaisesRegex(ValueError, "output limit"):
            crypto.inflate(zlib.compress(bytes(10000)), 10)
        with self.assertRaises(ValueError):
            crypto.inflate(zlib.compress(b"payload")[:-1], 100)

    def firmware(self, entries=None):
        entries = entries or [("dev_flash/data/font/test.TTF", b"font"), ("dev_flash/sys/external/flashATRAC.pic", b"audio")]
        inner = tar_bytes(entries)
        package, _ = certified([b"package header", b"content header", inner], category=3)
        source = self.root / "firmware.PUP"
        source.write_bytes(pup_bytes(tar_bytes([("dev_flash_000.tar.aa.test", package)])))
        return source

    def test_pup_to_firmware_without_external_program_and_validator_before_publish(self):
        source, home = self.firmware(), self.root / "home"
        before = source.read_bytes()
        with patch("launcher.setup_pipeline.firmware_validator") as validator, \
             patch("subprocess.Popen", side_effect=AssertionError("external process")):
            importer.extract_firmware(source, home, self.keys, self.logs.append, self.cancel)
        self.assertEqual((home / "dev_flash/data/font/test.TTF").read_bytes(), b"font")
        self.assertTrue(validator.call_args.args[0].name == "dev_flash")
        self.assertEqual(source.read_bytes(), before)
        with patch("launcher.setup_pipeline.firmware_validator", side_effect=ValueError("unsupported firmware")):
            with self.assertRaisesRegex(ValueError, "unsupported firmware"):
                importer.extract_firmware(source, self.root / "invalid", self.keys, self.logs.append, self.cancel)
        self.assertFalse((self.root / "invalid/dev_flash").exists())

    def test_firmware_recovers_publish_before_ledger_save_and_rejects_modified_output(self):
        source, home = self.firmware(), self.root / "home"
        with patch("launcher.setup_pipeline.firmware_validator"):
            for _ in range(2):
                importer.extract_firmware(source, home, self.keys, self.logs.append, self.cancel)
            (home / "dev_flash/data/font/test.TTF").write_bytes(b"modified")
            with self.assertRaisesRegex(ValueError, "changed"):
                importer.extract_firmware(source, home, self.keys, self.logs.append, self.cancel)
        self.assertEqual((home / "dev_flash/data/font/test.TTF").read_bytes(), b"modified")

    def test_pup_checks_header_and_segment_hmac_and_missing_key(self):
        source = self.firmware()
        wire = source.read_bytes()
        for offset in (40, len(wire) - 1):
            bad = bytearray(wire)
            bad[offset] ^= 1
            source.write_bytes(bad)
            with self.assertRaises(ValueError):
                importer.firmware_archive(source, self.keys, self.cancel)
        source.write_bytes(wire)
        with self.assertRaisesRegex(ValueError, "pup_hmac"):
            importer.firmware_archive(source, crypto.KeyStore(), self.cancel)

    def test_firmware_rejects_traversal_links_and_duplicate_files(self):
        link = tarfile.TarInfo()
        link.type, link.linkname = tarfile.SYMTYPE, "../../outside"
        for index, entries in enumerate(([('dev_flash/../escape', b'bad')], [('dev_flash/link', link)],
                                         [('dev_flash/a', b'1'), ('dev_flash/a', b'2')])):
            source = self.firmware(entries)
            with self.assertRaises(ValueError):
                importer.extract_firmware(source, self.root / f"bad-{index}", self.keys, self.logs.append, self.cancel)
            self.assertFalse((self.root / f"bad-{index}/dev_flash").exists())
        self.assertFalse((self.root / "escape").exists())

    def test_update_retail_and_debug_chunked_unaligned_payload_and_title_guard(self):
        files = [("PARAM.SFO", sfo()), ("USRDIR/EBOOT.BIN", b"synthetic executable"), ("USRDIR/Patched/test.dat", bytes(range(255)) * 5)]
        for debug in (True, False):
            source = self.root / f"update-{debug}.pkg"
            source.write_bytes(pkg_bytes(files, debug=debug))
            home = self.root / f"update-{debug}"
            before = source.read_bytes()
            with patch.object(importer, "CHUNK", 127):
                importer.extract_update(source, home, "BCES00647", self.logs.append, self.cancel)
            for name, data in files:
                self.assertEqual((home / name).read_bytes(), data)
            self.assertEqual(source.read_bytes(), before)
            bad_home = self.root / "wrong-title"
            with self.assertRaisesRegex(ValueError, "title"):
                importer.extract_update(source, bad_home, "BCUS98209", self.logs.append, self.cancel)
            self.assertFalse(bad_home.exists())

    def test_update_rejects_unsafe_paths_collisions_and_corrupt_ranges_before_writing(self):
        for names in (("../escape",), ("USRDIR/A", "usrdir/a"), ("CON",), ("C:/escape",)):
            files = [("PARAM.SFO", sfo()), ("USRDIR/EBOOT.BIN", b"code")] + [(name, b"unsafe") for name in names]
            source = self.root / "bad.pkg"
            source.write_bytes(pkg_bytes(files))
            with self.assertRaises(ValueError):
                importer.extract_update(source, self.root / "output", "BCES00647", self.logs.append, self.cancel)
            self.assertFalse((self.root / "output").exists())
        source.write_bytes(pkg_bytes([("PARAM.SFO", sfo()), ("USRDIR/EBOOT.BIN", b"code")])[:-1])
        with self.assertRaisesRegex(ValueError, "sizes"):
            importer.extract_update(source, self.root / "output", "BCES00647", self.logs.append, self.cancel)

    def test_cancellation_and_archive_size_limits(self):
        source = self.firmware()
        self.event.set()
        with self.assertRaises(Cancelled):
            importer.extract_firmware(source, self.root / "cancel", self.keys, self.logs.append, self.cancel)
        self.event.clear()
        source = self.root / "update.pkg"
        source.write_bytes(pkg_bytes([("PARAM.SFO", sfo()), ("USRDIR/EBOOT.BIN", b"code")]))
        with patch.object(importer, "MAX_FIRMWARE", 1):
            with self.assertRaisesRegex(ValueError, "extraction limit"):
                importer.extract_update(source, self.root / "output", "BCES00647", self.logs.append, self.cancel)
        self.assertFalse((self.root / "output").exists())

    def test_cancel_during_update_removes_partial_file_and_can_retry(self):
        source = self.root / "update.pkg"
        source.write_bytes(pkg_bytes([("PARAM.SFO", sfo()), ("USRDIR/EBOOT.BIN", bytes(5000))]))
        output = self.root / "output"
        def cancel_mid_file():
            partial = list(output.glob("USRDIR/*.import-partial-*"))
            if partial:
                raise Cancelled("cancel during file")
        with self.assertRaises(Cancelled):
            importer.extract_update(source, output, "BCES00647", self.logs.append, cancel_mid_file)
        self.assertFalse((output / "USRDIR/EBOOT.BIN").exists())
        self.assertEqual(list(output.rglob("*.import-partial-*")), [])
        importer.extract_update(source, output, "BCES00647", self.logs.append, self.cancel)
        self.assertEqual((output / "USRDIR/EBOOT.BIN").read_bytes(), bytes(5000))

    def test_complete_pipeline_with_encrypted_pup_and_update_then_resume(self):
        game = self.root / "game"
        (game / "PS3_GAME/USRDIR").mkdir(parents=True)
        (game / "PS3_GAME/PARAM.SFO").write_bytes(sfo())
        (game / "PS3_GAME/USRDIR/EBOOT.BIN").write_bytes(b"disc not selected")
        wire, expected = certified([b"update executable"], npdrm=True)
        update = self.root / "update.pkg"
        update.write_bytes(pkg_bytes([("PARAM.SFO", sfo()), ("USRDIR/EBOOT.BIN", wire)]))
        inputs = pipeline.Inputs(str(game), str(self.firmware()), str(self.keyfile), str(update))
        expected_digest = hashlib.sha256(expected).hexdigest()
        def identify(title, digest):
            if (title, digest) != ("BCES00647", expected_digest):
                raise ValueError("unsupported executable")
            return PROFILES[0]
        with patch.object(pipeline, "firmware_validator"), patch.object(pipeline, "identify", side_effect=identify), \
             patch("subprocess.Popen", side_effect=AssertionError("external process")):
            session, state = pipeline.prepare(inputs, self.root, self.logs.append, self.event, build=False)
            self.assertEqual(Path(state["paths"]["eboot_elf"]).read_bytes(), expected)
            _, resumed = pipeline.prepare(inputs, self.root, self.logs.append, self.event, build=False, resume=session)
            self.assertEqual(state["done"], resumed["done"])
            self.assertNotIn(ROOT_KEY.hex(), (session / "state.json").read_text())
            self.keyfile.write_text(self.keyfile.read_text() + "\n")
            with self.assertRaisesRegex(ValueError, "Inputs changed"):
                pipeline.prepare(inputs, self.root, self.logs.append, self.event, build=False, resume=session)

    def test_unsupported_elf_is_not_published_as_completed_stage(self):
        game = self.root / "game"
        (game / "PS3_GAME/USRDIR").mkdir(parents=True)
        (game / "PS3_GAME/PARAM.SFO").write_bytes(sfo())
        wire, _ = certified([b"unknown executable"])
        (game / "PS3_GAME/USRDIR/EBOOT.BIN").write_bytes(wire)
        inputs = pipeline.Inputs(str(game), str(self.firmware()), str(self.keyfile))
        with patch.object(pipeline, "firmware_validator"):
            with self.assertRaisesRegex(ValueError, "Unsupported"):
                pipeline.prepare(inputs, self.root, self.logs.append, self.event, build=False)
        session = next((self.root / "runtime/setup/sessions").iterdir())
        state = json.loads((session / "state.json").read_text())
        self.assertNotIn("prepare executable", state["done"])
        self.assertFalse((session / "executable/EBOOT.elf").exists())
        self.assertFalse((session / "running.lock").exists())


if __name__ == "__main__":
    unittest.main()
