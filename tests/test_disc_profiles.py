"""Synthetic cross-region identities and generated-image alias safety."""
import hashlib
from pathlib import Path
import tempfile
import unittest

from launcher.game_profiles import identify, identify_elf
from tools.codegen.disc100_profiles import (EU100_SHA256, US100_SHA256,
                                   US_EMBEDDED_ALIASES, canonicalize_embedded)


class DiscProfileTests(unittest.TestCase):
    def test_regions_and_families_are_not_interchangeable(self):
        us = identify("BCUS98209", US100_SHA256)
        self.assertTrue(us.buildable)
        self.assertEqual(us.adapter_family, "quizworld-us100")
        self.assertEqual(identify_elf(US100_SHA256), us)
        self.assertEqual(dict(us.runtime_defaults)["BUZZ_NATIVE_COMPLETION"], "2")
        self.assertEqual(us.compiler_options(), identify("BCES00647", EU100_SHA256).compiler_options())
        for title, digest in (("BCUS98209", EU100_SHA256), ("BCES00647", US100_SHA256),
                              ("BCUS98209", "0" * 64)):
            with self.assertRaises(ValueError):
                identify(title, digest)

    def test_aliases_verify_every_blob_before_mutating_anything(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "actual1.elf").write_bytes(b"synthetic1")
            (root / "actual2.elf").write_bytes(b"synthetic2")
            aliases = [("actual1.elf", "role1.elf", hashlib.sha256(b"synthetic1").hexdigest()),
                       ("actual2.elf", "role2.elf", "0" * 64)]
            with self.assertRaisesRegex(ValueError, "differs"):
                canonicalize_embedded(root, aliases)
            self.assertTrue((root / "actual1.elf").is_file())
            self.assertFalse((root / "role1.elf").exists())
            aliases[1] = ("actual2.elf", "role2.elf", hashlib.sha256(b"synthetic2").hexdigest())
            evidence = canonicalize_embedded(root, aliases)
            self.assertEqual(len(evidence), 2)
            self.assertEqual((root / "role2.elf").read_bytes(), b"synthetic2")
            self.assertFalse((root / "actual1.elf").exists())

    def test_alias_paths_collisions_and_duplicate_roles_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            digest = hashlib.sha256(b"synthetic").hexdigest()
            (root / "actual.elf").write_bytes(b"synthetic")
            (root / "role.elf").write_bytes(b"keep")
            for aliases in ([("actual.elf", "role.elf", digest)],
                            [("../actual.elf", "new.elf", digest)],
                            [("actual.elf", "a.elf", digest), ("ACTUAL.ELF", "b.elf", digest)],
                            [("a.elf", "role.elf", digest), ("b.elf", "ROLE.ELF", digest)]):
                with self.assertRaises(ValueError):
                    canonicalize_embedded(root, aliases)
            self.assertEqual((root / "role.elf").read_bytes(), b"keep")
            self.assertEqual(len(US_EMBEDDED_ALIASES), 8)


if __name__ == "__main__":
    unittest.main()
