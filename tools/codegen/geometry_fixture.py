"""Optional private calibration snapshots for the geometry oracle generators.

The build's production adapters (*-original.inc) are derived from the lift
alone. Captured SPU entry snapshots under analysis/ are game-derived and never
committed; they pin the reviewed calibration and seed the private test
oracles. A fresh checkout has none, so a generator then writes its production
output and skips its test oracle. A snapshot that is present but differs from
its pin still stops generation.
"""
import hashlib
from pathlib import Path


def optional_fixture(path: Path, pin: str, unreviewed: str):
    """Return the pinned snapshot bytes, or None when it is not available."""
    if not path.exists():
        print(f"[codegen] {path.name} not present: production output only, test oracle skipped")
        return None
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != pin:
        raise SystemExit(unreviewed)
    return data


def text_sha256(path: Path) -> str:
    """Hash a tracked source file as committed (LF), whatever the checkout's
    core.autocrlf setting wrote to disk."""
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
