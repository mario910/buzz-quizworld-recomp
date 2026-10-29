"""Read-only validation of locally supplied files. No game downloads/decryption."""
import argparse
import hashlib
from pathlib import Path
import sys
import struct

import buzz_paths
from launcher.model import title_id


def verify(paths, *, elf_only=False, expected_hash=buzz_paths.EXPECTED_ELF_SHA256):
    elf = Path(paths["eboot_elf"])
    if not elf.is_file() or elf.stat().st_size > 256 * 1024 * 1024:
        raise ValueError("eboot_elf must be the decrypted update 1.02 EBOOT.elf (not EBOOT.BIN).")
    with elf.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != expected_hash:
        raise ValueError(f"Unsupported EBOOT.elf SHA-256: {digest}; expected {expected_hash} (BCES00647 1.02).")
    if elf.stem != "EBOOT":
        raise ValueError("Name the decrypted input EBOOT.elf; generation uses the EBOOT basename.")
    if elf_only:
        return
    game = Path(paths["game_root"])
    sfo = game / "PS3_GAME/PARAM.SFO"
    if not (game / "PS3_GAME/USRDIR").is_dir() or not sfo.is_file() or sfo.stat().st_size > 1024 * 1024:
        raise ValueError("game_root must contain PS3_GAME/USRDIR and PS3_GAME/PARAM.SFO.")
    if title_id(sfo.read_bytes()) != "BCES00647":
        raise ValueError("Only the BCES00647 disc dump is supported.")
    flash = Path(paths["dev_flash"])
    if not (flash / "data/font").is_dir():
        raise ValueError("dev_flash must contain data/font from your installed PS3 firmware.")
    atrac = flash / "sys/external/flashATRAC.pic"
    if not atrac.is_file():
        raise ValueError("dev_flash is missing sys/external/flashATRAC.pic (required by the standard audio build).")
    # Check .text by the same parser/pin the audio generator uses; no output is written.
    import importlib.util
    spec = importlib.util.spec_from_file_location("mstream_modules", Path(__file__).with_name("generate-mstream-modules.py"))
    modules = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(modules)
    if hashlib.sha256(modules.text_section(atrac)).hexdigest() != modules.PINNED["atrac"]:
        raise ValueError("Unsupported flashATRAC.pic code; see README firmware requirements. Do not bypass its hash guard.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf-only", action="store_true", help="Check only the generation input, before writing any lifts.")
    args = parser.parse_args()
    try:
        verify(buzz_paths.load(), elf_only=args.elf_only)
    except (OSError, ValueError, KeyError, TypeError, IndexError, struct.error, SystemExit) as exc:
        print(f"Validation failed: {exc}", file=sys.stderr)
        return 1
    print("Validated BCES00647 update 1.02" + (" EBOOT.elf." if args.elf_only else " game, EBOOT.elf and firmware inputs."))
    return 0


if __name__ == "__main__":
    sys.exit(main())
