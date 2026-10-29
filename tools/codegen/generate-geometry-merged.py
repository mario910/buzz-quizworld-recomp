"""Opt-in geometry lift: keep all entry points, merge contiguous basic blocks.

Writes a separate experiment, never replaces the reference port/spu lift.
No new return-ABI assumptions, float shortcuts, or cross-job/list fusion.
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from pathlib import Path
import subprocess
import sys
import re

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "ps3recomp/tools"
sys.path.insert(0, str(TOOLS))
from build_spu_workloads import debase_includes, fnv1a64

raw = ROOT / "analysis/raw-jobs/buzz_job_6eba.bin"
bounds = raw.with_suffix(".functions.json")
reference = ROOT / "port/spu/buzz_job_6eba/spu_recomp.c"
dest = ROOT / "analysis/merged-geometry"
data = raw.read_bytes()
if len(data) != 58624 or fnv1a64(data) != 0x6EBA872A91705ED9:
    raise ValueError("Expected the captured Quiz World 1.02 geometry image")
subprocess.run([sys.executable, str(TOOLS / "spu_lifter.py"), str(raw),
    "--functions", str(bounds), "--output", str(dest),
    "--symbol-prefix", "buzz_job_6eba_", "--code-end", "0xCB80",
    "--merge-chunks"], check=True)
for name in ("spu_recomp.h", "spu_recomp.c"):
    debase_includes(dest / name)
pattern = r'\{ 0x[0-9A-F]+u, buzz_job_6eba_spu_func_[0-9A-F]+, "[^"]+" \}'
old = set(re.findall(pattern, reference.read_text()))
new = set(re.findall(pattern, (dest / "spu_recomp.c").read_text()))
if not old or old != new:
    raise ValueError("Merged lift changed the registered entry points")
print(f"Verified {len(old)} unchanged registrations; experimental output: {dest}")
