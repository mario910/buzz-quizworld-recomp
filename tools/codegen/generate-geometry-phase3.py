"""Build the pinned whole-driver audit and incremental native-setup adapter."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "analysis/native-geometry/phase3"
SOURCE = ROOT / "port/spu/buzz_job_6eba/spu_recomp.c"
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PARENT_PIN = "0cb78a3d3170bf81921021199fcd2144272858635224f3135208f76c76113d99"


def main():
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != PIN:
        raise SystemExit("Unreviewed image-41 lift")
    parent_generator = ROOT / "tools/codegen/generate-geometry-phase2.py"
    if hashlib.sha256(parent_generator.read_bytes()).hexdigest() != PARENT_PIN:
        raise SystemExit("Unreviewed phase-2 adapter; audit before updating the parent pin")
    subprocess.run([sys.executable, str(parent_generator)], check=True)
    parent = ROOT / "analysis/native-geometry/phase2"
    source = (parent / "spu_recomp.c").read_text()
    if "#define BUZZ_GEOMETRY_IMPLEMENTED_MASK 6015" not in source:
        raise SystemExit("Expected all eleven validated phase-2 groups")
    counts = {}
    for old, new in (("spu_wrch", "buzz_geometry_job_wrch"),
                     ("spu_rdch", "buzz_geometry_job_rdch"),
                     ("spu_rchcnt", "buzz_geometry_job_rchcnt")):
        source, counts[old] = re.subn(rf"\b{old}\(", new + "(", source)
        if not counts[old]:
            raise SystemExit(f"Missing channel sites: {old}")
    source = re.sub(r"\bSPU_IB_DISPATCH\b", "buzz_geometry_job_indirect", source)
    source = source.replace("g_spu_trampoline_fn = spu_indirect_branch;",
                            "g_spu_trampoline_fn = buzz_geometry_job_indirect_target(ctx);")
    name = "buzz_job_6eba_spu_func_00001C18"
    tail_match = re.search(r"(?ms)^void buzz_job_6eba_spu_func_00002008\(spu_context\* ctx\) \{(.*?)^\}", source)
    if not tail_match or tail_match[1].count("ctx->gpr[65] = spu_ceqi(ctx->gpr[90], 0);") != 1:
        raise SystemExit("Unreviewed post-initializer driver boundary")
    tail = "ctx->gpr[65] = spu_ceqi(ctx->gpr[90], 0);" + tail_match[1].split(
        "ctx->gpr[65] = spu_ceqi(ctx->gpr[90], 0);", 1)[1]
    pattern = rf"(?ms)^void {name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    matches = list(re.finditer(pattern, source))
    if len(matches) != 1:
        raise SystemExit("Expected one pinned whole-driver root")
    source = re.sub(pattern, lambda m:
                    f"static void {name}_after_init(spu_context* ctx) {{{tail}}}\n"
                    f"static void {name}_native(spu_context* ctx) {{ "
                    f"buzz_geometry_job_native_start(ctx, {name}_after_init); }}\n"
                    f"static void {name}_reference(spu_context* ctx) {{{m[1]}}}\n"
                    f"void {name}(spu_context* ctx) {{ "
                    f"buzz_geometry_job_run(ctx, {name}_reference, {name}_native, "
                    f"buzz_geometry_job_native_eligible); }}", source, count=1)
    support = f'#include "{(ROOT / "port/native_geometry_job.h").as_posix()}"\n'
    support += "#define BUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE 1\n"
    support += "#define BUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE 1\n"
    source = source.replace('#include "spu_helpers.h"', '#include "spu_helpers.h"\n' + support, 1)
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "spu_recomp.c").write_text(source)
    (DEST / "spu_recomp.h").write_bytes((parent / "spu_recomp.h").read_bytes())
    print(f"Phase-3 reference/shadow adapter -> {DEST}; wrapped sites {counts}")


if __name__ == "__main__":
    main()
