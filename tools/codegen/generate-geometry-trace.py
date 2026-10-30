"""Generate a pinned, replay-only image-41 LS/MFC trace translation unit.

Instrument the lift at its access sites, preserving exact SPU instructions.
All derived code and traces remain private in analysis/.
"""
import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/spu/buzz_job_6eba"
DEST = ROOT / "analysis/native-geometry/trace-lift"
PINS = {"5ae9232d4f2d47004b687251b479b4515c1f2ea46e9f868332e3305a4a13434e",
        "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"}


def main():
    raw = (SOURCE / "spu_recomp.c").read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest not in PINS:
        raise SystemExit("Unreviewed geometry lift: audit before adding a trace pin")
    source = raw.decode()
    block_count = len(re.findall(r"^void buzz_job_6eba_spu_func_[0-9A-F]{8}\(spu_context\* ctx\) \{", source, re.M))
    header = (ROOT / "port/geometry_trace.h").as_posix()
    source = source.replace('#include "spu_helpers.h"', f'#include "spu_helpers.h"\n#define BUZZ_GEOMETRY_BLOCKS {block_count}\n#include "{header}"', 1)
    next_id = 0
    def block(m):
        nonlocal next_id
        block_id = next_id
        next_id += 1
        pc = int(m[1], 16)
        body = m[2]
        for original, replacement in (("spu_ls_read128", "geometry_read128"),
                                      ("spu_ls_write128", "geometry_write128"),
                                      ("spu_wrch", "geometry_wrch")):
            body = body.replace(f"{original}(ctx,", f"{replacement}(ctx, 0x{pc:X},")
        name = "buzz_job_6eba_spu_func_" + m[1]
        return (f"static void {name}_trace_body(spu_context* ctx);\n"
                f"void {name}(spu_context* ctx) {{\n"
                f"    geometry_enter(ctx, 0x{pc:X}, {block_id});\n"
                f"    {name}_trace_body(ctx);\n"
                f"    geometry_leave(ctx, 0x{pc:X}, {block_id});\n}}\n"
                f"static void {name}_trace_body(spu_context* ctx) {{\n{body}}}")
    source, count = re.subn(r"^void buzz_job_6eba_spu_func_([0-9A-F]{8})\(spu_context\* ctx\) \{\n(.*?)^\}", block, source, flags=re.M | re.S)
    if count < 100 or re.search(r"\bspu_ls_(?:read|write)(?:8|16|32|64)\(", source):
        raise SystemExit("Unreviewed access form or incomplete instrumentation")
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "spu_recomp.c").write_text(source)
    (DEST / "spu_recomp.h").write_bytes((SOURCE / "spu_recomp.h").read_bytes())
    (DEST / "source.sha256").write_text(digest + "\n")
    print(f"Instrumented {count} blocks -> {DEST}")


if __name__ == "__main__":
    main()
