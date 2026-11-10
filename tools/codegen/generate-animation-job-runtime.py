"""Wrap the pinned, qualified image-40 fallback with the optional host job.

Derived game code stays private. This does not alter kernels or add lifted
entry points; it only instruments transport and wraps the existing job entry.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--fallback", type=Path, default=root / "analysis/native-image40/spu_recomp.c")
parser.add_argument("--output", type=Path, required=True, help="Fresh private adapter directory")
parser.add_argument("--in-place", action="store_true",
                    help="Replace only the hash-checked fallback in its own private directory")
args = parser.parse_args()
base = args.fallback.resolve()
out = args.output.resolve()
private = (root / "analysis/native-geometry/phase4").resolve()
if args.in_place:
    if out != base.parent or base != root / "analysis/native-image40/spu_recomp.c":
        raise SystemExit("In-place wrapping is restricted to analysis/native-image40/spu_recomp.c")
elif not out.is_relative_to(private) or out == private:
    raise SystemExit("Output must be a fresh subdirectory of analysis/native-geometry/phase4")
if out.exists() and not args.in_place:
    raise SystemExit("Refusing to overwrite existing adapter/evidence")
raw = (root / "port/spu/buzz_job_abe1/spu_recomp.c").read_bytes()
fallback = base.read_bytes()
raw_sha = hashlib.sha256(raw).hexdigest()
fallback_sha = hashlib.sha256(fallback).hexdigest()
if raw_sha != "1b45bc6b4a53d7d4647714475a72e183b9b4e7a79d4e032f2e8925ed728f60ba":
    raise SystemExit("Image-40 EU100 reference changed; re-audit required")
if fallback_sha != "059ecd7462edba914ca95a0a242cd8c95c19199b83ed0c1e633b829e5685a0ca":
    raise SystemExit("Qualified Stage F fallback changed; re-audit required")
source = fallback.decode("utf-8")
anchor = '#include "spu_helpers.h"'
old_fp = '#include "../../port/native_image40_fp.h"'
if source.count(anchor) != 1 or source.count(old_fp) != 1:
    raise SystemExit("Unexpected helper/FP adapter include")

def include(name):
    return '#include "' + os.path.relpath(root / "port" / name, out).replace("\\", "/") + '"'

source = source.replace(old_fp, include("native_image40_fp.h"))
source = source.replace(anchor, anchor + "\n" + include("native_geometry_job.h") + "\n" +
                        include("native_animation_runtime.h") + "\n#define BUZZ_ANIMATION_JOB_DRIVER_ABI 1")
entry = "buzz_job_abe1_spu_func_00000010"
definition = f"void {entry}(spu_context* ctx) {{"
if source.count(definition) != 1:
    raise SystemExit("Unexpected animation job entry")
source = source.replace(definition, f"static void {entry}_retained(spu_context* ctx) {{")
source += f"\nvoid {entry}(spu_context* ctx) {{ buzz_animation_job_run(ctx,{entry}_retained); }}\n"
sites = {}
for old, new in (("spu_wrch", "buzz_geometry_job_wrch"),
                 ("spu_rdch", "buzz_geometry_job_rdch"),
                 ("spu_rchcnt", "buzz_geometry_job_rchcnt")):
    source, sites[old] = re.subn(rf"\b{old}\(", new + "(", source)
if not sites["spu_wrch"] or not sites["spu_rdch"]:
    raise SystemExit("Missing original channel sites")
source = re.sub(r"\bSPU_IB_DISPATCH\b", "buzz_geometry_job_indirect", source)
source = source.replace("g_spu_trampoline_fn = spu_indirect_branch;",
                        "g_spu_trampoline_fn = buzz_geometry_job_indirect_target(ctx);")
out.mkdir(parents=True, exist_ok=args.in_place)
(out / "spu_recomp.c").write_text(source, encoding="utf-8", newline="\n")
if not args.in_place:
    shutil.copyfile(base.parent / "spu_recomp.h", out / "spu_recomp.h")
(out / "adapter.json").write_text(json.dumps({
    "abi": 1, "reference_sha256": raw_sha, "fallback_sha256": fallback_sha,
    "fallback": str(base), "channel_sites": sites,
    "adapter_sha256": hashlib.sha256((out / "spu_recomp.c").read_bytes()).hexdigest(),
    "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    "default": "off", "option": "BUZZ_ANIMATION_JOB", "scope": "image40 EU100 only",
}, indent=2) + "\n", encoding="utf-8")
print(f"Pinned retained image-40 fallback + whole-job host adapter -> {out}")
