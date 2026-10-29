"""Check the SPURS boot regression from a timed run, NOT menu/gameplay success."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent
stderr = Path(sys.argv[1]) if len(sys.argv) > 1 else max(
    (root / "logs").glob("*.stderr.log"), key=lambda p: p.stat().st_mtime)
stdout = stderr.with_name(stderr.name.replace(".stderr.log", ".stdout.log"))
err = stderr.read_text(errors="replace")
out = stdout.read_text(errors="replace")
progress = [tuple(map(int, m)) for m in re.findall(
    r"decompressed-jobs=(\d+) bytes=(\d+) flips=(\d+) presented=(\d+)", err)]
checks = {
    "manager wake from real SPU job": "flagEA=0x701EB380 pattern=0xFFFF got=" in err,
    "main-thread completion flag": "flagEA=0x701EC300 pattern=0x0001 got=" in err,
    "render loop beyond startup": bool(progress) and max(p[2] for p in progress) > 100,
    "asset decompression beyond old stall": bool(progress) and max(p[0] for p in progress) >= 402,
    "no unlifted SPURS jobs": "dispatch MISS" not in err,
    "no unresolved SPU branches": "BRANCH-TO-0" not in err,
    "no invalid chains": "invalid JOBLIST" not in out and "-- malformed?" not in out,
    "no recorded native exception": "[buzz-crash]" not in err,
}
for name, ok in checks.items():
    print(f"{'PASS' if ok else 'FAIL'}: {name}")
print("Logs:", stderr.name, stdout.name)
print("Last counters:", progress[-1] if progress else "none")
print("This does not confirm input, main menu, sound, or gameplay.")
sys.exit(0 if all(checks.values()) else 1)
