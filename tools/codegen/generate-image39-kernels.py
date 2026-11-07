"""Route image 39's hot leaf kernels through port/native_stagef.cpp.

Input is the image 39 translation unit as built (tools/codegen/generate-image39-native.py
output, analysis/native-image39/spu_recomp.c). Leaf kernels:
  0x05D0/0x05C0, loop 0x0760/0x0750: fma/fm/shift-heavy arithmetic leaf.
          Job share must be measured on the selected workload; it is not a
          fixed property of this source. The optional native bounds driver
          admits only the pinned EU 1.00 caller and disjoint bounded inputs.
For each, the lifted body is kept as `..._reference` and a wrapper under the
original name calls buzz_stagef_run(ctx, reference, native, kernel). The
hook (port/native_image39_kernels.h) is included into this translation unit.
The separate optional descriptor-request hook is carried from the input;
selecting this arithmetic leaf's Stage F bit does not enable that request.
Reproducible derived output: never edit port/spu.
"""

from pathlib import Path as _Path
import sys as _sys
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from pathlib import Path
import re
import shutil
import hashlib

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "analysis/native-image39"
DEST = ROOT / "analysis/native-image39-kernels"
PREFIX = "buzz_job_c0bc_spu_func_0000"
# (entry pc, native function, kernel id in native_stagef.cpp)
original = (ROOT / "port/spu/buzz_job_c0bc/spu_recomp.c").read_bytes()
pc = {"599125db31e1195056c4d176e3344cdd453afaab073ae1794d4bdddf7b976854": "05D0",
      "c8e0e95c527ac062d019f5385b27fdafba20d08306e32f4977fbdf313ea79611": "05C0"}.get(hashlib.sha256(original).hexdigest())
if pc is None:
    raise SystemExit("Unreviewed image39 kernel profile")
KERNELS = ((pc, "buzz_image39_native_05d0", 3),)

source = (SOURCE / "spu_recomp.c").read_text(encoding="utf-8")
header = "native_image39_kernels.h"  # resolved by the game target's port/ include path
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
        bounds_profile = 100 if pc == "05C0" else 0
        wrapper += f'\n#define BUZZ_IMAGE39_BOUNDS_PROFILE {bounds_profile}\n#include "{header}"\n'
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
