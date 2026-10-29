"""Reviewed disc identities and content-checked SPU role aliases.

US and EU SPU programs are byte-identical; only their container offsets differ.
Canonical names are tooling roles, NOT assertions about their source offsets.
No PPU address is relocated by this table.
"""
import hashlib
from pathlib import Path

EU100_SHA256 = "6b07344c81a669174586ea637634f3b522eb91a7824e9e88623581b88d0405a8"
US100_SHA256 = "fb78b9e6d30ba5eaa76b7e31bc3afa838736dcb5cde40fc602a628f2305ebb5d"

# (actual extracted name, canonical EU role name, full embedded ELF SHA-256)
US_EMBEDDED_ALIASES = tuple(
    (f"spu_{i:04d}_at_{actual:08X}.elf", f"spu_{i:04d}_at_{canonical:08X}.elf", digest)
    for i, actual, canonical, digest in (
        (0, 0x716D80, 0x717B80, "1293590f024afd1f25c4fbf2984ccdac9086253d1c26bfd56e4a35de77642c75"),
        (1, 0x71C500, 0x71D300, "58aeb9e122f05a90b12169f01317543e440c3ebdc231d227b8c8724fa1f0bf43"),
        (2, 0x72CB80, 0x72D980, "602845be51ad13fdc6c3afb921b61c07a0c9b0ce6811525a6eaa30d6abf2c9e1"),
        (3, 0x736300, 0x737100, "f049c47e203fa94a855d3c3446546167ddd1d7364b9caef0fe08e41d4bc8b012"),
        (4, 0x737180, 0x737F80, "dfef313c710665f8c64b12bfe9ad688af4baa6cf29f80990a939d746643e8eb5"),
        (5, 0x739380, 0x73A180, "def638dc7109a070dd750cde834075aecb1f7444076bd34880018dddbf00408d"),
        (6, 0x7FE980, 0x7FF780, "97ed747bc902ed1023695d6d0a765bbf67a983977747d34d52f908dad4be2b5e"),
        (7, 0x809280, 0x80A080, "4923f234865f8a7d2bedede1b6f968cf18f0f4809127d9662d97ab8d1df4edfd"),
    )
)

US_RAW_ROLES = {
    0x739980: ("buzz_job_abe1", 40), 0x744500: ("buzz_job_2b4b", 33),
    0x74DF80: ("buzz_job_b3ae", 42), 0x758480: ("buzz_job_19d0", 43),
    0x763100: ("buzz_job_c0bc", 39), 0x76A880: ("buzz_job_6874", 34),
    0x785B00: ("buzz_job_6eba", 41), 0x794180: ("buzz_job_14b4", 36),
    0x7C1700: ("buzz_job_39d9", 38), 0x7C2180: ("buzz_job_d2c1", 35),
    0x7CFB80: ("disc_job_007d0980", 45), 0x7D9280: ("buzz_job_d690", 44),
    0x7E2900: ("buzz_job_40b5", 37), 0x7F1300: ("buzz_signal", 32),
    0x7F1C00: ("disc_job_007f2a00", 46), 0x7F3D80: ("disc_job_007f4b80", 47),
}


def canonicalize_embedded(directory, aliases=US_EMBEDDED_ALIASES):
    """Validate ALL aliases before changing any generated analysis file."""
    directory = Path(directory)
    if len({a.casefold() for a, _, _ in aliases}) != len(aliases) or len({b.casefold() for _, b, _ in aliases}) != len(aliases):
        raise ValueError("Duplicate SPU alias")
    evidence = []
    for source, target, digest in aliases:
        if any(Path(name).name != name or name in (".", "..") or "/" in name or "\\" in name for name in (source, target)):
            raise ValueError("SPU aliases must be simple filenames")
        original, canonical = directory / source, directory / target
        if original.is_symlink() or canonical.exists() or canonical.is_symlink():
            raise ValueError("SPU alias source/target collision or symlink")
        if hashlib.sha256(original.read_bytes()).hexdigest() != digest:
            raise ValueError("Embedded SPU differs from the reviewed canonical role: " + source)
        evidence.append(dict(actual=source, canonical=target, sha256=digest))
    for item in evidence:
        (directory / item["actual"]).rename(directory / item["canonical"])
    return evidence
