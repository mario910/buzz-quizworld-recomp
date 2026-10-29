"""Route image 39's hot leaf kernels through port/native_stagef.cpp.

Input is the image 39 translation unit as built (generate-image39-native.py
output, analysis/native-image39/spu_recomp.c). Leaf kernels:
  0x05D0  loop 0x0760 (fma/fm/shift heavy; ~45% of an image 39 job), called
          with brsl from 0x2044, leaves through `bi $0`
For each, the lifted body is kept as `..._reference` and a wrapper under the
original name calls buzz_stagef_run(ctx, reference, native, kernel). The
native code (port/native_image39_kernels.h) is included into this translation
unit; it sets MXCSR RZ+FTZ+DAZ itself for its hardware float operations, so it
does not depend on SPURS_NATIVE_IMAGE39_FP.
Reproducible derived output: never edit port/spu.
"""
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "analysis/native-image39"
DEST = ROOT / "analysis/native-image39-kernels"
PREFIX = "buzz_job_c0bc_spu_func_0000"
# (entry pc, native function, kernel id in native_stagef.cpp)
KERNELS = (("05D0", "buzz_image39_native_05d0", 3),)

source = (SOURCE / "spu_recomp.c").read_text(encoding="utf-8")
header = (ROOT / "port/native_image39_kernels.h").as_posix()
included = False
for pc, native, kernel in sorted(KERNELS, key=lambda k: int(k[0], 16)):
    name = PREFIX + pc
    match = re.search(rf"(?m)^void {name}\(spu_context\* ctx\) \{{", source)
    if not match:
        raise SystemExit(f"missing {name}")
    body_end = source.index("\n}", match.end())
    body = source[match.end():body_end]
    if re.search(r"spu_(?:wrch|rdch|mfc)", body):
        raise SystemExit(f"0x{pc} has side effects beyond LS and registers")
    wrapper = ""
    if not included:
        wrapper += f'\n#include "{header}"\n'
        wrapper += ("extern void buzz_stagef_run(spu_context* ctx, void (*reference)(spu_context*),\n"
                    "                            int (*native)(spu_context*), int kernel);\n"
                    "extern int buzz_stagef_mode;\n")
        included = True
    wrapper += f"""void {name}(spu_context* ctx) {{
    if (!buzz_stagef_mode) {{ {name}_reference(ctx); return; }}
    buzz_stagef_run(ctx, {name}_reference, {native}, {kernel});
}}
"""
    source = (source[:match.start()] + f"void {name}_reference(spu_context* ctx) {{"
              + body + "\n}\n" + wrapper + source[body_end + 2:])
DEST.mkdir(parents=True, exist_ok=True)
(DEST / "spu_recomp.c").write_text(source, encoding="utf-8", newline="\n")
shutil.copyfile(SOURCE / "spu_recomp.h", DEST / "spu_recomp.h")
print(f"Image 39 kernel wrappers ({', '.join(k[0] for k in KERNELS)}) -> {DEST}")
