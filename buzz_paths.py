"""Locations of the user's own game files, from buzz-paths.json (not in git)."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EXPECTED_ELF_SHA256 = "9303ccce9bc223ca6da2eb837256ef08be6f1f460dcc6d5191889de7a4eee241"


def load():
    path = ROOT / "buzz-paths.json"
    if not path.exists():
        raise SystemExit(f"Missing {path}. Copy buzz-paths.example.json to buzz-paths.json "
                         "and set the paths to your own game files.")
    paths = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(paths, dict):
        raise ValueError("buzz-paths.json must be a JSON object")
    for key in ("game_root", "dev_flash", "eboot_elf"):
        value = paths.get(key)
        if not isinstance(value, str) or not value.strip() or "\0" in value:
            raise ValueError(f"buzz-paths.json: '{key}' must be a nonempty path string")
    return paths


def eboot_elf():
    return Path(load()["eboot_elf"])
